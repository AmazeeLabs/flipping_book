<?php

/**
 * @file
 * Flipping Book Pages.
 */

/**
 * Flipping Book Management form.
 */
function flipping_book_management_form($form, &$form_state) {

  $form['#attached'] = array(
    'css' => array(
      drupal_get_path('module', 'flipping_book') . '/css/flipping_book.css',
    )
  );

  $max_upload_size = ini_get('upload_max_filesize');

  $form['filters']['fb'] = array(
    '#tree' => TRUE,
    '#theme' => 'table',
    '#header' => array(t('Title'), t('File'), t('Import')),
    '#rows' => array(),
  );

  $title = array(
    '#type' => 'textfield',
    '#title' => t('Title'),
    '#required' => TRUE,
  );

  $file = array(
    '#type' => 'file',
    '#title' => t('Flipping Book'),
    '#description' => t('Allowed extension: zip. Files must be less than @max_upload_size', array('@max_upload_size' => $max_upload_size)),
  );

  $submit = array(
    '#type' => 'submit',
    '#value' => t('Import'),
  );

  $form['filters']['fb'][] = array(
    'title' => &$title,
    'file' => &$file,
    'submit' => &$submit,
  );

  $form['filters']['fb']['#rows'][] = array(
    array('data' => &$title),
    array('data' => &$file),
    array('data' => &$submit),
  );

  unset($title);
  unset($file);
  unset($submit);

  $form['#suffix'] = views_embed_view('flipping_books', 'block_1');

  return $form;
}

/**
 * Validation callback.
 *
 * @see flipping_book_management_form()
 */
function flipping_book_management_form_validate($form, &$form_state) {
  $file = file_save_upload('fb', array('file_validate_extensions' => array('zip')));

  if ($file) {
    $form_state['values']['fb'][0]['file'] = $file;
  }
  else {
    form_set_error('file', t('No file was uploaded.'));
  }
}

/**
 * Submit callback.
 * @see flipping_book_management_form()
 */
function flipping_book_management_form_submit($form, &$form_state) {
  $title = $form_state['values']['fb'][0]['title'];

  $file = $form_state['values']['fb'][0]['file'];
  $filepath = drupal_realpath($file->uri);

  if (module_exists('transliteration')) {
    $dir = pathinfo(transliteration_clean_filename($file->filename), PATHINFO_FILENAME);
  }
  else {
    $dir = pathinfo(_flipping_book_clean_filename($file->filename), PATHINFO_FILENAME);
  }

  $fdir = flipping_book_dir();
  file_prepare_directory($fdir, FILE_CREATE_DIRECTORY);
  $destination = file_destination($fdir . '/' . $dir, FILE_EXISTS_RENAME);

  //$zip = new ZipArchive();
  //$res = $zip->open($filepath);
  $old_locale = setlocale(LC_CTYPE, '0');
  $new_locale = setlocale(LC_CTYPE, 'en_US.UTF-8');
  $args = array(
    escapeshellarg($filepath),
    '-d ' . escapeshellarg(drupal_realpath($destination) . '/'),
  );
  setlocale(LC_CTYPE, $old_locale);
  $output = NULL;
  $return = NULL;
  exec("unzip " . implode(' ', $args), $output, $return);

  //$zip->extractTo(drupal_realpath($destination) . '/');
  // It may be that the index.html is located in some deeper folder in the
  // archive, so search for it. For that we will use a queue with the FIFO
  // principle: we always process elements at the begining of the queue, and
  // we add new elements to process at the end of the queue. The elements to
  // process represent the folders. We will start with the initial destination
  // folder in the queue. As soon as we find an index.html file, we will just
  // break.
  $folders_to_check = array($destination);
  $index_html_found = FALSE;
  while (!empty($folders_to_check)) {
    $current_folder = reset($folders_to_check);
    // If we found the index.html, we stop here.
    if (is_file($current_folder . '/index.html')) {
      $destination = $current_folder;
      $index_html_found = TRUE;
      break;
    }
    // Add all the folders from the current folder to the queue.
    $scandir_list = scandir($current_folder);
    // The 0 and 1 represent the '.' and '..', so we skip them.
    unset($scandir_list[0], $scandir_list[1]);
    foreach ($scandir_list as $item_name) {
      if (is_dir($current_folder . '/' . $item_name)) {
        $folders_to_check[] = $current_folder . '/' . $item_name;
      }
    }
    // Remove the first folder from the queue, it was processed.
    array_shift($folders_to_check);
  }

  // If we did not find the file, then we show an error and do not save the
  // flipping book.
  if (!$index_html_found) {
    drupal_set_message(t('The index.html file could not be found in the archive for your Flipping Book.'), 'error');
    return;
  }

  // FIX KEN-621 - Rename Flipping books with backslashes in filename to correct forward-slash based folder directory
  $fileDirectory = file_stream_wrapper_get_instance_by_uri($destination)->realpath();
  $fileIterator = new FilesystemIterator($fileDirectory);
  foreach ($fileIterator as $fileInfo) {
    $origFilename = $fileInfo->getFilename();
    // Contains backslash in filename
    if (strpos($origFilename, '\\') !== FALSE && substr($origFilename, -1) !== '\\') {
      $fileName = file_destination($fileDirectory . '/' . str_replace('\\', '/', $origFilename), FILE_EXISTS_REPLACE);
      $filePath = dirname($fileName);
      $dirWriteable = file_prepare_directory($filePath, FILE_CREATE_DIRECTORY || FILE_MODIFY_PERMISSIONS);
      if ($dirWriteable) {
        file_unmanaged_move($fileDirectory . '/' . $origFilename, $fileName);
      }
    }
  }

  $record = array(
    'title' => $title,
    'dir' => str_replace(flipping_book_dir() . '/', '', $destination),
    'created' => REQUEST_TIME,
  );

  if (drupal_write_record('flipping_book', $record)) {
    drupal_set_message(t('Your Flipping Book has been imported.'));
  }
  else {
    drupal_set_message(t('There was an error saving your Flipping Book.'), 'error');
  }

}

/**
 * Helper function to sanitize flipping_book filename
 */
function _flipping_book_clean_filename($filename) {
  // Replace whitespace.
  $filename = str_replace(' ', '_', $filename);
  // Remove remaining unsafe characters.
  $filename = preg_replace('![^0-9A-Za-z_.-]!', '', $filename);
  // Remove multiple consecutive non-alphabetical characters.
  $filename = preg_replace('/(_)_+|(\.)\.+|(-)-+/', '\\1\\2\\3', $filename);
  // Force lowercase to prevent issues on case-insensitive file systems.
  $filename = strtolower($filename);

  return $filename;
}
